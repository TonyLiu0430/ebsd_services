package route

import (
	"context"
	"go_backend/db"
	"go_backend/ent"
	"go_backend/ent/user"
	"go_backend/routeRegister"
	"os"
	"regexp"
	"time"

	"github.com/gin-contrib/sessions"
	"github.com/gin-gonic/gin"
	"github.com/golang-jwt/jwt/v5"
	"github.com/google/uuid"
	"golang.org/x/crypto/bcrypt"
)

var usernameRe = regexp.MustCompile(`^[a-zA-Z0-9_]+$`)
var passwordRe = regexp.MustCompile(`^[^\s]+$`)

func vaildate(username string, password string) bool {
	return usernameRe.MatchString(username) && passwordRe.MatchString(password) && len(username) >= 3 && len(username) <= 30 && len(password) >= 4 && len(password) <= 100
}

func hashPassword(password string) (string, error) {
	hash, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	return string(hash), err
}

func checkPassword(password, hash string) bool {
	return bcrypt.CompareHashAndPassword([]byte(hash), []byte(password)) == nil
}

func makeJWT(userID string) (string, error) {
	secret := os.Getenv("JWT_SECRET")
	if secret == "" {
		panic("JWT_SECRET isn't set")
	}

	bsecret := []byte(secret)

	claims := jwt.MapClaims{
		"sub": userID,
		"exp": time.Now().Add(48 * time.Hour).Unix(),
		"iat": time.Now().Unix(),
	}

	token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
	return token.SignedString(bsecret)
}

func parseJWT(tokenString string) (string, error) {
	secret := os.Getenv("JWT_SECRET")
	if secret == "" {
		panic("JWT_SECRET isn't set")
	}

	token, err := jwt.Parse(tokenString, func(token *jwt.Token) (any, error) {
		return []byte(secret), nil
	}, jwt.WithValidMethods([]string{jwt.SigningMethodHS256.Alg()}))
	if err != nil || !token.Valid {
		return "", err
	}

	claims, ok := token.Claims.(jwt.MapClaims)
	if !ok {
		return "", jwt.ErrTokenInvalidClaims
	}

	sub, ok := claims["sub"].(string)
	if !ok || sub == "" {
		return "", jwt.ErrTokenInvalidClaims
	}

	return sub, nil
}

func init() {
	routeRegister.Register = append(routeRegister.Register, func(rg *gin.RouterGroup) {
		route := rg.Group("user")

		route.GET("/check", func(c *gin.Context) {
			session := sessions.Default(c)
			token, ok := session.Get("jwt").(string)
			if !ok || token == "" {
				c.JSON(401, gin.H{
					"error": "not logged in",
				})
				return
			}

			userID, err := parseJWT(token)
			if err != nil {
				session.Delete("jwt")
				_ = session.Save()
				c.JSON(401, gin.H{
					"error": "invalid session",
				})
				return
			}

			id, err := uuid.Parse(userID)
			if err != nil {
				c.JSON(401, gin.H{
					"error": "invalid session",
				})
				return
			}

			found, err := db.DB.User.Get(context.Background(), id)
			if err != nil {
				c.JSON(401, gin.H{
					"error": "invalid session",
				})
				return
			}

			c.JSON(200, gin.H{
				"user": gin.H{
					"id":       found.ID,
					"username": found.Username,
				},
			})
		})

		route.POST("/logout", func(c *gin.Context) {
			session := sessions.Default(c)
			session.Delete("jwt")
			if err := session.Save(); err != nil {
				c.JSON(500, gin.H{
					"error": "failed to clear session",
				})
				return
			}
			c.JSON(200, gin.H{
				"msg": "ok",
			})
		})

		route.POST("/register", func(c *gin.Context) {
			var body struct {
				Username string `json:"username"`
				Password string `json:"password"`
			}
			if err := c.ShouldBindJSON(&body); err != nil {
				c.JSON(400, gin.H{
					"error": "invalid request body",
				})
				return
			}

			if body.Username == "" || body.Password == "" {
				c.JSON(400, gin.H{
					"error": "username and password cannot be empty",
				})
				return
			}

			if !vaildate(body.Username, body.Password) {
				c.JSON(400, gin.H{
					"error": "invalid username or password",
				})
				return
			}

			hash, err := hashPassword(body.Password)
			if err != nil {
				c.JSON(500, gin.H{
					"error": "failed to hash password",
				})
				return
			}

			created, err := db.DB.User.
				Create().
				SetUsername(body.Username).
				SetPassword(hash).
				Save(context.Background())

			if err != nil {
				if ent.IsConstraintError(err) {
					c.JSON(409, gin.H{
						"error": "username already exists",
					})
					return
				}
				c.JSON(500, gin.H{
					"error": "failed to create user",
				})
				return
			}

			token, err := makeJWT(created.ID.String())
			if err != nil {
				c.JSON(500, gin.H{
					"error": "failed to create token",
				})
				return
			}

			session := sessions.Default(c)
			session.Set("jwt", token)

			if err := session.Save(); err != nil {
				c.JSON(500, gin.H{
					"error": "failed to save session",
				})
				return
			}

			c.JSON(201, gin.H{
				"user": gin.H{
					"id":       created.ID,
					"username": created.Username,
				},
			})
		})

		route.POST("/login", func(c *gin.Context) {
			var body struct {
				Username string `json:"username"`
				Password string `json:"password"`
			}
			if err := c.ShouldBindJSON(&body); err != nil {
				c.JSON(400, gin.H{
					"error": "invalid request body",
				})
				return
			}

			if !vaildate(body.Username, body.Password) {
				c.JSON(400, gin.H{
					"error": "invalid username or password",
				})
				return
			}

			user, err := db.DB.User.
				Query().
				Where(user.UsernameEQ(body.Username)).
				First(context.Background())

			if err != nil {
				c.JSON(401, gin.H{
					"error": "invalid username or password",
				})
				return
			}

			if !checkPassword(body.Password, user.Password) {
				c.JSON(401, gin.H{
					"error": "invalid username or password",
				})
				return
			}

			token, err := makeJWT(user.ID.String())
			if err != nil {
				c.JSON(500, gin.H{
					"error": "failed to create token",
				})
				return
			}

			session := sessions.Default(c)
			session.Set("jwt", token)
			if err := session.Save(); err != nil {
				c.JSON(500, gin.H{
					"error": "failed to save session",
				})
				return
			}

			c.JSON(200, gin.H{
				"user": gin.H{
					"id":       user.ID,
					"username": user.Username,
				},
			})
		})
	})
}

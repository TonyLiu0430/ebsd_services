package route

import (
	"context"
	"encoding/json"
	"go_backend/auth"
	"go_backend/db"
	"go_backend/ent/user"
	"go_backend/routeRegister"
	"io"
	"log"
	"regexp"
	"strings"

	"github.com/gin-contrib/sessions"
	"github.com/gin-gonic/gin"
	"golang.org/x/crypto/bcrypt"
)

var usernameRe = regexp.MustCompile(`^[a-zA-Z0-9_]+$`)
var passwordRe = regexp.MustCompile(`^[[:graph:]]+$`)

func vaildate(username string, password string) bool {
	return usernameRe.MatchString(username) && passwordRe.MatchString(password) && len(username) >= 3 && len(username) <= 30 && len(password) >= 8 && len(password) <= 100
}

func hashPassword(password string) (string, error) {
	hash, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	return string(hash), err
}

func checkPassword(password, hash string) bool {
	return bcrypt.CompareHashAndPassword([]byte(hash), []byte(password)) == nil
}

func init() {
	routeRegister.Register = append(routeRegister.Register, func(rg *gin.RouterGroup) {
		route := rg.Group("user")
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

			if strings.TrimSpace(body.Username) == "" || strings.TrimSpace(body.Password) == "" {
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

			user, err := db.DB.User.
				Query().
				Where(user.UsernameEQ(body.Username)).
				Select(user.FieldUsername, user.FieldPassword).
				First(context.Background())

		})
		route.GET("/google/callback", func(c *gin.Context) {
			session := sessions.Default(c)

			if c.Query("state") != session.Get("OauthState") {
				log.Println("Invalid csrf token from IP:", c.ClientIP(), "=>", c.Query("state"), "!=", session.Get("OauthState"))
				c.JSON(401, gin.H{
					"error": "invalid csrf token",
				})
				return
			}

			baseUrl := session.Get("BaseUrl").(string)

			if baseUrl == "" {
				c.JSON(500, gin.H{
					"error": "BaseUrl is not set in session",
				})
				return
			}

			session.Delete("BaseUrl")

			session.Delete("OauthState")
			session.Save()

			code := c.Query("code")

			token, err := auth.GoogleOauthConfig(baseUrl).Exchange(context.Background(), code)

			if err != nil {
				c.JSON(500, gin.H{
					"error": "internal server error: " + err.Error(),
				})
				return
			}

			client := auth.GoogleOauthConfig(baseUrl).Client(context.Background(), token)

			response, err := client.Get("https://people.googleapis.com/v1/people/me?personFields=emailAddresses")

			if err != nil {
				c.JSON(500, gin.H{
					"error": "fail to get userInfo from google",
				})
				return
			}

			responseData, err := io.ReadAll(response.Body)

			if err != nil {
				c.JSON(500, gin.H{
					"error": "fail to read response from google api",
				})
				return
			}

			var userInfo map[string]any

			if err := json.Unmarshal(responseData, &userInfo); err != nil {
				c.JSON(500, gin.H{
					"error": "userInfo from google parse failed : Invaild Json",
				})
				return
			}

			email, ok := userInfo["emailAddresses"].([]any)[0].(map[string]any)["value"].(string)

			if !ok {
				c.JSON(500, gin.H{
					"error": "userInfo from google parse failed : field email error",
				})
				return
			}

			userId, err := db.DB.User.
				Create().
				SetEmail(email).
				OnConflictColumns(user.FieldEmail).
				UpdateNewValues().
				ID(context.Background())

			if err != nil {
				c.JSON(500, gin.H{
					"error": "internal server error: " + err.Error(),
				})
				return
			}

			session.Set("session", userId)

			// db := database.GetDB(c)

			// var user models.User

			// if result := db.Model(&models.User{}).Where(&models.User{Email: email}).FirstOrCreate(&user); result.Error != nil {
			// 	fmt.Println(result.Error)

			// 	c.JSON(500, gin.H{
			// 		"error": "internal server error",
			// 	})
			// 	return
			// }

			// if result := db.Model(&models.User{}).Where(&models.User{Email: email}).Update("last_login", time.Now()); result.Error != nil {
			// 	fmt.Println(result.Error)

			// 	c.JSON(500, gin.H{
			// 		"error": "internal server error",
			// 	})
			// 	return
			// }

			// session.Set("Email", email)

			// redirect := session.Get("Redirect").(string)

			// if redirect == "" || redirect[0] != '/' {
			// 	redirect = "/"
			// }

			// session.Delete("Redirect")
			// session.Save()

			// fmt.Println("Redirect to => ", redirect)

			// c.Redirect(http.StatusFound, redirect)
		})
	})
}

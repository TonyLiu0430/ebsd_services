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

	"github.com/gin-contrib/sessions"
	"github.com/gin-gonic/gin"
)

func init() {
	routeRegister.Register = append(routeRegister.Register, func(rg *gin.RouterGroup) {
		route := rg.Group("user")
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

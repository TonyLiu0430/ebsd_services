package main

import (
	"context"
	"log"
	"net/http"
	"os"
	"time"

	"go_backend/db"
	_ "go_backend/route"
	"go_backend/routeRegister"

	"github.com/gin-gonic/gin"

	"github.com/gin-contrib/sessions"
	"github.com/gin-contrib/sessions/cookie"
)

func main() {
	time.Sleep(3 * time.Second) // wait for database

	client := db.DB

	if err := client.Schema.Create(context.Background()); err != nil {
		log.Fatalf("failed creating schema resources: %v", err)
	}

	r := gin.Default()

	cookieStore := cookie.NewStore([]byte(os.Getenv("AUTH_SECRET")))
	r.Use(sessions.Sessions("ginSession", cookieStore))
	cookieStore.Options(sessions.Options{
		Path:     "/",
		MaxAge:   0,
		HttpOnly: true,
		Secure:   false,
		SameSite: http.SameSiteLaxMode,
	})

	rg := r.Group("")
	for _, route := range routeRegister.Register {
		route(rg)
	}

	rg.GET("/", func(c *gin.Context) {
		c.JSON(200, gin.H{
			"msg": "Hello World! from golang",
		})
	})

	r.Run()
}

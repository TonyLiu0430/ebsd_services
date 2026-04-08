package main

import (
	"context"
	"log"
	"time"

	"go_backend/db"
	"go_backend/routeRegister"

	"github.com/gin-gonic/gin"
)

func main() {
	time.Sleep(3 * time.Second)

	client := db.DB

	if err := client.Schema.Create(context.Background()); err != nil {
		log.Fatalf("failed creating schema resources: %v", err)
	}

	r := gin.Default()

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

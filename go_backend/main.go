package main

import (
	"context"
	"log"

	"go_backend/ent"
	"go_backend/routeRegister"

	"github.com/gin-gonic/gin"
)

func main() {
	client, err := ent.Open("sqlite3", "file:ent?mode=memory&cache=shared&_fk=1")
	if err != nil {
		log.Fatalf("failed opening connection to sqlite: %v", err)
	}
	defer client.Close()

	if err := client.Schema.Create(context.Background()); err != nil {
		log.Fatalf("failed creating schema resources: %v", err)
	}

	r := gin.Default()

	rg := r.Group("")
	for _, route := range routeRegister.Register {
		route(rg)
	}

	r.Run()
}

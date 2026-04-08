package db

import (
	"go_backend/ent"
	"log"
	"os"

	_ "github.com/lib/pq"
)

func connectDB() *ent.Client {
	connect_str := os.Getenv("DATABASE_CONNECTION_STRING")
	if connect_str == "" {
		panic("DATABASE_CONNECTION_STRING isn't set")
	}
	client, err := ent.Open("postgres", connect_str)
	if err != nil {
		log.Fatalf("failed opening connection to database: %v", err)
	}
	return client
}

var DB = connectDB()

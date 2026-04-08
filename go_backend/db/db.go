package db

import (
	"go_backend/ent"
	"log"

	_ "github.com/mattn/go-sqlite3"
)

func connectDB() *ent.Client {
	client, err := ent.Open("sqlite3", "file:ent?mode=memory&cache=shared&_fk=1")
	if err != nil {
		log.Fatalf("failed opening connection to sqlite: %v", err)
	}
	return client
}

var DB = connectDB()

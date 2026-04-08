package schema

import (
	"time"

	"entgo.io/ent"
	"entgo.io/ent/schema/edge"
	"entgo.io/ent/schema/field"
	"github.com/google/uuid"
)

// Message holds the schema definition for the Message entity.
type Message struct {
	ent.Schema
}

// Fields of the Message.
func (Message) Fields() []ent.Field {
	return []ent.Field{
		field.UUID("id", uuid.UUID{}).
			Default(uuid.New).
			Immutable(),
		field.Time("created_at").
			Default(time.Now).
			Immutable(),
		field.String("content"),
		field.Enum("role").
			Values("system", "user", "assistant"),
	}
}

// Edges of the Message.
func (Message) Edges() []ent.Edge {
	return []ent.Edge{
		edge.To("chat_session", ChatSession.Type),
	}
}

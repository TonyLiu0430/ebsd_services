package schema

import (
	"time"

	"entgo.io/ent"
	"entgo.io/ent/schema/edge"
	"entgo.io/ent/schema/field"
	"github.com/google/uuid"
)

// ChatSession holds the schema definition for the ChatSession entity.
type ChatSession struct {
	ent.Schema
}

// Fields of the ChatSession.
func (ChatSession) Fields() []ent.Field {
	return []ent.Field{
		field.UUID("id", uuid.UUID{}).
			Default(uuid.New).
			Immutable(),
		field.Time("created_at").
			Default(time.Now).
			Immutable(),
		field.Time("updated_at").
			Default(time.Now).
			UpdateDefault(time.Now),
	}
}

// Edges of the ChatSession.
func (ChatSession) Edges() []ent.Edge {
	return []ent.Edge{
		edge.To("user", User.Type),
	}
}

package auth

import (
	"os"

	"golang.org/x/oauth2"
	"golang.org/x/oauth2/google"
)

func GoogleOauthConfig(baseUrl string) *oauth2.Config {
	if os.Getenv("GOOGLE_CLIENT_ID") == "" || os.Getenv("GOOGLE_CLIENT_SECRET") == "" {
		panic("missing GOOGLE_CLIENT_ID or GOOGLE_CLIENT_SECRET for google oauth")
	}
	return &oauth2.Config{
		ClientID:     os.Getenv("GOOGLE_CLIENT_ID"),
		ClientSecret: os.Getenv("GOOGLE_CLIENT_SECRET"),
		RedirectURL:  baseUrl + "/api/login/google/callback",
		Scopes: []string{
			"https://www.googleapis.com/auth/userinfo.email",
			"https://www.googleapis.com/auth/userinfo.profile",
		},
		Endpoint: google.Endpoint,
	}
}

package route

import (
	"go_backend/routeRegister"

	"github.com/gin-gonic/gin"
)

func init() {
	routeRegister.Register = append(routeRegister.Register, func(rg *gin.RouterGroup) {
		route := rg.Group("msg")
		route.GET("/", func(c *gin.Context) {
			c.JSON(200, gin.H{
				"msg": "ok",
			})
		})
	})
}

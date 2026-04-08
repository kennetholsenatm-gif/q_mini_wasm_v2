package main

import (
	"encoding/hex"
	"fmt"
	"os"
)

func main() {
	hexStr := "f5a565058a357339f2e00006244742235527f388b19379a4325daf9d005fac70"
	bytes, err := hex.DecodeString(hexStr)
	if err != nil {
		fmt.Println(err)
		return
	}
	os.Stdout.Write(bytes)
}

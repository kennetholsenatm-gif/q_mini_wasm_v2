package main

import (
	"testing"
)

func TestRunpodServerlessGraphQLSaveInput(t *testing.T) {
	in := runpodServerlessGraphQLSaveInput(
		map[string]any{"dockerArgs": "bash -c true"},
		"tpl-name",
		"docker.io/x/y:z",
	)
	if in["volumeInGb"] != 0 || in["isServerless"] != true {
		t.Fatalf("expected volumeInGb 0 and isServerless true, got %#v", in)
	}
	if in["dockerArgs"] != "bash -c true" {
		t.Fatalf("dockerArgs override: %v", in["dockerArgs"])
	}
	if in["containerDiskInGb"] != 50 {
		t.Fatalf("expected default disk 50, got %v", in["containerDiskInGb"])
	}
	if in["name"] != "tpl-name" || in["imageName"] != "docker.io/x/y:z" {
		t.Fatal("name/imageName")
	}
	env, ok := in["env"].([]map[string]any)
	if !ok || env == nil {
		t.Fatalf("env should be empty slice, got %#v", in["env"])
	}
}

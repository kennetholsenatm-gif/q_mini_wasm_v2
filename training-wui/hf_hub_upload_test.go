package main

import "testing"

func TestHFHubCommitURL(t *testing.T) {
	t.Parallel()
	u, err := hfHubCommitURL("org/model", "main")
	if err != nil {
		t.Fatal(err)
	}
	want := "https://huggingface.co/api/models/org/model/commit/main"
	if u != want {
		t.Fatalf("got %q want %q", u, want)
	}
}

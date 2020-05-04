package main

import (
	"testing"

	. "github.com/maxatome/go-testdeep"
)

func TestExpandBuckets(t *testing.T) {
	list := []float64{0.1, 0.5, 1, 10}
	res, err := expandBuckets(" 0.1;0.5;1; 10")
	CmpNoError(t, err)
	CmpDeeply(t, list, res)
}

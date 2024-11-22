package main

import (
	"testing"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestExpandBuckets(t *testing.T) {
	t.Parallel()
	list := []float64{0.1, 0.5, 1, 10}
	res, err := expandBuckets(" 0.1;0.5;1; 10")
	require.NoError(t, err)
	assert.Equal(t, list, res)
}

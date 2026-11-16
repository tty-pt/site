#!/bin/sh
set -eu
node tests/unit/test-lamina-bridge-routing.mjs
node tests/unit/test-webgl-fbo-bridge.mjs

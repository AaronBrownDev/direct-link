#!/bin/bash

set -e

sudo apt-get update

echo "Tidying go modules"
cd backend && go mod tidy && cd ..

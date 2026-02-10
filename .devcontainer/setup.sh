#!/bin/bash

set -e

sudo apt-get update

echo "Generating protobuf files"
cd backend && make proto && cd ..

echo "Tidying go modules"
cd backend && go mod tidy && cd ..

#!/bin/bash

echo 020000001000200030004000 | xxd -r -p | nc -u -p 12345 -w0 localhost 10000

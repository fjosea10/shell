#! /bin/bash

if ! [[ -x hfsh2 ]]; then
    echo "hfsh2 executable does not exist"
    exit 1
fi

../tester/run-tests.sh $*



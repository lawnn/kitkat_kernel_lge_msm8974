#!/bin/bash

export BUILD_TARGET=AOSP
. l01f.config

time ./_build-bootimg.sh $1

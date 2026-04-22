#!/usr/bin/env bash

git ls-files \
  '*.cpp' '*.h' '*.hpp' \
  ':!external/**' \
| xargs clang-format-18 -i

#!/usr/bin/env python3

import base64

f = open("favicons-b64.txt")

favicons = []

for line in f:
    favicons.append(line)

line = 1

for favicon in favicons:
    outfile = open("favicons/" + str(line) + ".png", "wb")
    outfile.write(base64.b64decode(favicon))
    outfile.close()
    line += 1

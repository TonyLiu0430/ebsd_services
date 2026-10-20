#!/usr/bin/env node

import { stdin, stdout } from 'node:process'
import { renderGrainCharts } from '../src/batchRenderer.js'

let source = ''
stdin.setEncoding('utf8')
for await (const chunk of stdin) source += chunk

stdout.write(JSON.stringify(renderGrainCharts(JSON.parse(source || '{}'))))

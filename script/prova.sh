#!/bin/bash

SCRIPTPATH="$( cd "$(dirname "$0")" ; pwd -P )"
echo ${SCRIPTPATH}

PATH="$( cd "$(dirname "$0")" ; cd .. ; pwd -P )"
echo ${PATH}

SPATH="$( pwd -P )"/file
echo ${SPATH}

#DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
#echo ${DIR}

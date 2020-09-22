#!/usr/bin/env bash

# exit when any command fails
set -e


# [[ $(hostname) =~ remote1[1-2].chalmers.se ]] ||  { echo "You need to compile and test your program at remote11/remote12.chalmers.se before submission!"; exit 1; }

TEMPLATE_URL="https://chalmersuniversity.box.com/shared/static/bc3ql4482w6s2521029tujww8jaet02a.zip" 
TEMPLATE_NAME="lab2-template"
TEST_PINTOS_DIR="$(pwd)/pintos"

if [ ! -e ${TEST_PINTOS_DIR} ]; then
  echo "Cannot find your pintos directory"
  exit 1
fi

function testSuccess {
  if [ "$1" != "0" ]; then
    echo "Command failed!"
    exit 1
  fi
}

wget -q -O "${TEMPLATE_NAME}.zip" "$TEMPLATE_URL" > /dev/null
unzip "${TEMPLATE_NAME}.zip"

source ~/.bashrc
chmod +x $TEMPLATE_NAME/src/utils/pintos*
chmod +x $TEMPLATE_NAME/src/utils/backtrace
sleep 1

SUBMISSION="lab2-$(whoami)"
[[ -e $SUBMISSION ]] && rm -r "$SUBMISSION"
mkdir "$SUBMISSION"
cd "$SUBMISSION"

cp -r /chalmers/users/hannajd/pintos/src .
cp ${TEST_PINTOS_DIR}/src/threads/thread.* ./src/threads/
testSuccess $?
cp ${TEST_PINTOS_DIR}/src/devices/timer.* ./src/devices/
testSuccess $?
cp ${TEST_PINTOS_DIR}/src/devices/batch-scheduler.* ./src/devices/
testSuccess $?

cd src/threads 
make clean > /dev/null
make
make check  > /dev/null

cd ../../..
zip -r "${SUBMISSION}.zip" "$SUBMISSION/"
rm -r "$SUBMISSION"
echo
echo "===================================================="
echo ">> Submission prepared SUCCESSFULLY: ${SUBMISSION}.zip"
echo ">> Upload to Canvas together with your report!"
echo "===================================================="
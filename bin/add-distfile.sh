#!/bin/sh
#

MD5="md5 -q"

FILE="$1"
VERSION="$2"
OUTPUT="$3"

/bin/echo -n "Enter dropbox share URL for ${PACKAGE_FILE}: "
read URL
DATE=`date`
CSUM=`${MD5} ${FILE}`
echo "Checksum is: " $CSUM

mv $OUTPUT $OUTPUT.orig
sed '/LASTLINE/d' $OUTPUT.orig > $OUTPUT

(
echo "{"
echo "  'version': '$VERSION', "
echo "  'url': '$URL', "
echo "  'checksum': '$CSUM', "
echo "  'date': '$DATE' "
echo "},"
) >> $OUTPUT

echo "{ 'LASTLINE': true }]" >> $OUTPUT

exit 0

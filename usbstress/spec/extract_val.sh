#!/bin/sh


if [ "$#" != "1" ]
then
	echo "missing filename";
	exit 1;
fi

listval=$(grep -A 3 'Bulk or'  $1  | egrep -v "(Bulk|Input|buffer|in|--|<td></td>)" )

echo "$listval";

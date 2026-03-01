echo abc > /dev/nulldump
cat nulldump.in > /dev/nulldump
head --bytes=20 /dev/nulldump
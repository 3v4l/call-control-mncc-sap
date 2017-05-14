#/bin/sh
echo on Debian, codegen/cparser.py must call gccxml.real instead of gccxmlsudo!
cp ./mncc.h /tmp/mncc.h
h2xml ./mncc.h -c -o mncc.xml
xml2py mncc.xml -k d -v -o mncc.py

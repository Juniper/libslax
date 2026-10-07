#!/usr/bin/env python
import os
import sys
import setup_test
import libbxml
# Memory debug specific
libbxml.debugMemory(1)
import libxslt

basedir = os.path.dirname(os.path.realpath(__file__))

styledoc = libbxml.parseFile("%s/test.xsl" % basedir)
style = libxslt.parseStylesheetDoc(styledoc)
doc = libbxml.parseFile("%s/test.xml" % basedir)
result = style.applyStylesheet(doc, None)
style.saveResultToFilename("foo", result, 0)
os.remove("foo")
stringval = style.saveResultToString(result)
if (len(stringval) != 68):
  print("Error in saveResultToString")
  sys.exit(255)
style.freeStylesheet()
doc.freeDoc()
result.freeDoc()

# Memory debug specific
libxslt.cleanup()
if libbxml.debugMemory(1) == 0:
    print("OK")
else:
    print("Memory leak %d bytes" % (libbxml.debugMemory(1)))
    libbxml.dumpMemory()
    sys.exit(255)

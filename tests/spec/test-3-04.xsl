<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:variable name="x" select="1"/>
  <xsl:variable name="y">
    <xsl:number level="any" from="h1" count="h2 | h3 | h4"/>
  </xsl:variable>
</xsl:stylesheet>

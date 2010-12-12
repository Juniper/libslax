<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template name="one" match="one">
    <xsl:text>one</xsl:text>
  </xsl:template>
  <xsl:template name="two" match="two">
    <xsl:param name="three"/>
    <xsl:param name="four"/>
    <xsl:text>two</xsl:text>
  </xsl:template>
</xsl:stylesheet>

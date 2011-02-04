<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <top>
      <xsl:variable name="x" select="/test"/>
      <xsl:if test="not($x) and not(/foo/bar) and not(goo[zoo and not(moo)])">
        <not/>
      </xsl:if>
    </top>
  </xsl:template>
</xsl:stylesheet>

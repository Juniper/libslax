<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:variable name="x" select="concat(div, ./*, mod)"/>
  <xsl:variable name="y" select="div div div + mod mod mod"/>
  <xsl:template match="/">
    <output>
      <xsl:value-of select="concat(&quot;Here is the data: &quot;, ./*[1])"/>
    </output>
    <output>thisandthat</output>
  </xsl:template>
</xsl:stylesheet>

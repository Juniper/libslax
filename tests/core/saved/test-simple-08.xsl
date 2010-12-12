<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="doc" priority="100">
    <top>
      <xsl:apply-templates/>
    </top>
  </xsl:template>
  <xsl:template match="*" priority="10">
    <output pri="10">
      <xsl:value-of select="."/>
    </output>
  </xsl:template>
  <xsl:template match="*" priority="20">
    <output pri="20">
      <xsl:value-of select="."/>
    </output>
  </xsl:template>
  <xsl:template match="*" priority="5">
    <output pri="5">
      <xsl:value-of select="."/>
    </output>
  </xsl:template>
</xsl:stylesheet>

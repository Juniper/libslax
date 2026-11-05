<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:template match="/">
    <top>
      <xsl:variable name="x" select="1" mutable="yes"/>
      <xsl:variable name="y" select="$x"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="x" select="2"/>
      <xsl:variable name="z" select="$y"/>
      <out>
        <xsl:value-of select="$z"/>
      </out>
    </top>
  </xsl:template>
</xsl:stylesheet>

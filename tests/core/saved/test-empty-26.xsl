<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:param name="min" select="4"/>
  <xsl:param name="max" select="8"/>
  <xsl:template match="/">
    <top>
      <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(1, 10)">
        <up>
          <xsl:value-of select="."/>
        </up>
      </xsl:for-each>
      <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(10, 1)">
        <down>
          <xsl:value-of select="."/>
        </down>
      </xsl:for-each>
      <xsl:variable name="slax-dot-1" select="."/>
      <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence($min, $max)">
        <xsl:variable name="x" select="."/>
        <xsl:for-each select="$slax-dot-1">
          <up>
            <xsl:value-of select="$x"/>
          </up>
        </xsl:for-each>
      </xsl:for-each>
      <xsl:variable name="slax-dot-2" select="."/>
      <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence($max, $min)">
        <xsl:variable name="y" select="."/>
        <xsl:for-each select="$slax-dot-2">
          <down>
            <xsl:value-of select="$y"/>
          </down>
        </xsl:for-each>
      </xsl:for-each>
    </top>
  </xsl:template>
</xsl:stylesheet>

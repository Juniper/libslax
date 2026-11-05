<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:template match="/">
    <top>
      <xsl:variable name="x" mutable="yes">
        <node>one</node>
      </xsl:variable>
      <xsl:variable name="y" select="$x"/>
      <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="x">
        <node>two</node>
      </slax:set-variable>
      <xsl:variable name="z" select="$y"/>
      <xsl:copy-of select="$z"/>
    </top>
  </xsl:template>
</xsl:stylesheet>

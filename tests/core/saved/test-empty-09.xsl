<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:variable name="test" select="1"/>
  <xsl:template match="/">
    <out>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="&quot;this&quot;"/>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax" select="that"/>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax">
        <some>thing</some>
      </slax:trace>
      <slax:trace xmlns:slax="http://xml.libslax.org/slax">
        <xsl:if test="$test">
          <big>deal</big>
        </xsl:if>
        <xsl:if test="$test">
          <bigger>deal</bigger>
        </xsl:if>
      </slax:trace>
    </out>
  </xsl:template>
</xsl:stylesheet>

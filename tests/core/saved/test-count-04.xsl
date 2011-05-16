<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:variable name="root" select="1"/>
  <xsl:variable name="root2" select="2"/>
  <xsl:template match="/">
    <xsl:variable name="local-slash" select="&quot;top&quot;"/>
    <out>
      <xsl:variable name="nested-slash" select="&quot;nested&quot;"/>
      <xsl:call-template name="outer">
        <xsl:with-param name="this" select="&quot;vthis&quot;"/>
        <xsl:with-param name="that" select="&quot;vthat&quot;"/>
      </xsl:call-template>
    </out>
  </xsl:template>
  <xsl:template name="outer">
    <xsl:param name="this"/>
    <xsl:param name="that"/>
    <xsl:variable name="local-outer" select="name()"/>
    <xsl:variable name="local-outer2" select="2"/>
    <xsl:call-template name="inner">
      <xsl:with-param name="this" select="$this"/>
      <xsl:with-param name="that" select="&quot;calling inner&quot;"/>
    </xsl:call-template>
  </xsl:template>
  <xsl:template name="inner">
    <xsl:param name="this"/>
    <xsl:param name="that"/>
    <xsl:variable name="local-inner" select="name()"/>
    <xsl:variable name="local-inner2" select="2"/>
    <xsl:apply-templates select="*/*"/>
  </xsl:template>
  <xsl:template match="*">
    <xsl:variable name="local-star" select="name()"/>
    <xsl:variable name="local-star2" select="2"/>
    <xsl:message>
      <xsl:value-of select="concat(&quot;hit: &quot;, name())"/>
    </xsl:message>
    <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="debug" select="slax:debug()"/>
    <debug-data>
      <xsl:copy-of select="$debug"/>
    </debug-data>
  </xsl:template>
</xsl:stylesheet>

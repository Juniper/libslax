<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:jcs="http://xml.juniper.net/junos/commit-scripts/1.0" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" version="1.0" extension-element-prefixes="jcs slax-ext">
  <xsl:param name="how" select="&quot;and&quot;"/>
  <xsl:template match="/">
    <op-script-results>
      <xsl:value-of select="jcs:output(concat(&quot;This &quot;, $how, &quot; that&quot;))"/>
      <xsl:variable name="slax-temp-arg-1">
        <one/>
      </xsl:variable>
      <xsl:call-template name="test">
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="one" select="slax-ext:node-set($slax-temp-arg-1)"/>
      </xsl:call-template>
      <xsl:variable name="slax-temp-arg-2">
        <one>two</one>
      </xsl:variable>
      <xsl:call-template name="test">
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="one" select="slax-ext:node-set($slax-temp-arg-2)"/>
      </xsl:call-template>
      <xsl:variable name="slax-temp-arg-3">
        <one>
          <two>two</two>
        </one>
      </xsl:variable>
      <xsl:call-template name="test">
        <xsl:with-param xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="one" select="slax-ext:node-set($slax-temp-arg-3)"/>
      </xsl:call-template>
    </op-script-results>
  </xsl:template>
  <xsl:template name="test">
    <xsl:param name="one" select="&quot;null&quot;"/>
    <output>
      <xsl:copy-of select="$one"/>
    </output>
  </xsl:template>
</xsl:stylesheet>

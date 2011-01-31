<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" version="1.0" extension-element-prefixes="slax-ext">
  <xsl:param name="name" select="&quot;Poe&quot;"/>
  <xsl:variable name="favorites-temp-1">
    <name hidden="yes">Spillane</name>
    <name>Doyle</name>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="favorites" select="slax-ext:node-set($favorites-temp-1)"/>
  <xsl:template match="/">
    <out>
      <!-- Parameters are passed by name -->
      <xsl:call-template name="test">
        <xsl:with-param name="elt" select="&quot;author&quot;"/>
        <xsl:with-param name="name" select="$name"/>
      </xsl:call-template>
    </out>
  </xsl:template>
  <xsl:template name="test">
    <xsl:param name="name"/>
    <xsl:param name="elt" select="&quot;default&quot;"/>
    <xsl:variable name="slax-dot-1" select="."/>
    <xsl:for-each select="$favorites/name">
      <xsl:variable name="this" select="."/>
      <xsl:for-each select="$slax-dot-1">
        <xsl:choose>
          <xsl:when test="$name = $this and not(@hidden)">
            <xsl:element name="{$elt}">
              <xsl:copy-of select=".//author[name/last = $this]"/>
            </xsl:element>
          </xsl:when>
          <xsl:when test="$name = .">
            <xsl:message>
              <xsl:value-of select="concat(&quot;Hidden: &quot;, $name)"/>
            </xsl:message>
          </xsl:when>
        </xsl:choose>
      </xsl:for-each>
    </xsl:for-each>
  </xsl:template>
</xsl:stylesheet>

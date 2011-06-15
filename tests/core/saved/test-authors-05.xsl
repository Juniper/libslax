<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:template match="/">
    <xsl:variable name="pat1" select="&quot;(E)(.)(.)&quot;"/>
    <xsl:variable name="pat2" select="&quot;^Of(.{5})&quot;"/>
    <xsl:variable name="pat3" select="&quot;^Oft(.{5})&quot;"/>
    <out>
      <xsl:for-each select="authors/author">
        <result name="{name/last}">
          <xsl:call-template name="test">
            <xsl:with-param name="name" select="&quot;e1a&quot;"/>
            <xsl:with-param name="pat" select="$pat1"/>
            <xsl:with-param name="target" select="name/last"/>
          </xsl:call-template>
          <xsl:call-template name="test">
            <xsl:with-param name="name" select="&quot;e1b&quot;"/>
            <xsl:with-param name="pat" select="$pat1"/>
            <xsl:with-param name="target" select="name/last"/>
            <xsl:with-param name="opts" select="&quot;i&quot;"/>
          </xsl:call-template>
          <xsl:call-template name="test">
            <xsl:with-param name="name" select="&quot;e1c&quot;"/>
            <xsl:with-param name="pat" select="$pat1"/>
            <xsl:with-param name="target" select="name/last"/>
            <xsl:with-param name="opts" select="&quot;ib&quot;"/>
          </xsl:call-template>
          <xsl:call-template name="test">
            <xsl:with-param name="name" select="&quot;o2a&quot;"/>
            <xsl:with-param name="pat" select="$pat2"/>
            <xsl:with-param name="target" select="comment"/>
          </xsl:call-template>
          <xsl:call-template name="test">
            <xsl:with-param name="name" select="&quot;o2b&quot;"/>
            <xsl:with-param name="pat" select="$pat2"/>
            <xsl:with-param name="target" select="comment"/>
            <xsl:with-param name="opts" select="&quot;n&quot;"/>
          </xsl:call-template>
          <xsl:call-template name="test">
            <xsl:with-param name="name" select="&quot;o2c&quot;"/>
            <xsl:with-param name="pat" select="$pat2"/>
            <xsl:with-param name="target" select="comment"/>
            <xsl:with-param name="opts" select="&quot;n^$&quot;"/>
          </xsl:call-template>
          <xsl:call-template name="test">
            <xsl:with-param name="name" select="&quot;o3a&quot;"/>
            <xsl:with-param name="pat" select="$pat3"/>
            <xsl:with-param name="target" select="comment"/>
          </xsl:call-template>
          <xsl:call-template name="test">
            <xsl:with-param name="name" select="&quot;o3b&quot;"/>
            <xsl:with-param name="pat" select="$pat3"/>
            <xsl:with-param name="target" select="comment"/>
            <xsl:with-param name="opts" select="&quot;n&quot;"/>
          </xsl:call-template>
          <xsl:call-template name="test">
            <xsl:with-param name="name" select="&quot;o3c&quot;"/>
            <xsl:with-param name="pat" select="$pat3"/>
            <xsl:with-param name="target" select="comment"/>
            <xsl:with-param name="opts" select="&quot;n^$&quot;"/>
          </xsl:call-template>
        </result>
      </xsl:for-each>
    </out>
  </xsl:template>
  <xsl:template name="test">
    <xsl:param name="pat"/>
    <xsl:param name="name" select="$pat"/>
    <xsl:param name="target"/>
    <xsl:param name="opts"/>
    <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="re" select="slax:regex($pat, $target, $opts)"/>
    <xsl:element name="{$name}">
      <xsl:attribute name="pat">
        <xsl:value-of select="$pat"/>
      </xsl:attribute>
      <xsl:attribute name="target">
        <xsl:value-of select="$target"/>
      </xsl:attribute>
      <xsl:attribute name="opts">
        <xsl:value-of select="$opts"/>
      </xsl:attribute>
      <xsl:copy-of select="$re"/>
    </xsl:element>
  </xsl:template>
</xsl:stylesheet>

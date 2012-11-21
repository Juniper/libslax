<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax-ext slax">
  <xsl:variable name="list1-temp-1">
    <one>
      <xsl:value-of select="1"/>
    </one>
    <two>
      <xsl:value-of select="2"/>
    </two>
    <three>
      <xsl:value-of select="3"/>
    </three>
    <four>
      <xsl:value-of select="4"/>
    </four>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="list1" select="slax-ext:node-set($list1-temp-1)"/>
  <xsl:variable name="list2-temp-2">
    <a>
      <xsl:value-of select="1"/>
    </a>
    <b>
      <xsl:value-of select="2"/>
    </b>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="list2" select="slax-ext:node-set($list2-temp-2)"/>
  <xsl:param name="test"/>
  <xsl:template match="/">
    <top>
      <count1>
        <xsl:value-of select="count($list1)"/>
      </count1>
      <count1>
        <xsl:value-of select="count($list1/node())"/>
      </count1>
      <count2>
        <xsl:value-of select="count($list2)"/>
      </count2>
      <count2>
        <xsl:value-of select="count($list2/node())"/>
      </count2>
      <xsl:variable name="slax-ternary-1">
        <xsl:choose>
          <xsl:when test="$test">
            <xsl:copy-of select="$list1"/>
          </xsl:when>
          <xsl:otherwise>
            <xsl:copy-of select="$list2"/>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="res" select="slax:value($slax-ternary-1)"/>
      <countr>
        <xsl:value-of select="count($res)"/>
      </countr>
      <countr>
        <xsl:value-of select="count($res/node())"/>
      </countr>
    </top>
  </xsl:template>
</xsl:stylesheet>

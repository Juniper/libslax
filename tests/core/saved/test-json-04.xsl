<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:slax="http://xml.libslax.org/slax" xmlns:xutil="http://xml.libslax.org/xutil" version="1.0" extension-element-prefixes="slax-ext slax xutil">
  <xsl:variable name="count" select="10"/>
  <xsl:variable name="input-temp-1">
    <order>
      <fries type="number">
        <xsl:value-of select="1"/>
      </fries>
      <burger>
        <cheese>pepper jack</cheese>
        <cheese>american</cheese>
        <tomato>sliced</tomato>
      </burger>
      <fries type="number">
        <xsl:value-of select="2"/>
      </fries>
    </order>
    <tax type="number">
      <xsl:value-of select="4000"/>
    </tax>
    <order>
      <water>flat</water>
      <sandwich>
        <fish>grouper</fish>
      </sandwich>
    </order>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="input" select="slax-ext:node-set($input-temp-1)"/>
  <xsl:template match="/">
    <top>
      <xsl:variable name="full-temp-2">
        <full>
          <xsl:for-each xmlns:slax="http://xml.libslax.org/slax" select="slax:build-sequence(0, $count)">
            <xsl:copy-of select="$input"/>
          </xsl:for-each>
        </full>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="full" select="slax-ext:node-set($full-temp-2)"/>
      <input>
        <xsl:copy-of select="$input"/>
      </input>
      <output>
        <xsl:copy-of select="$full"/>
      </output>
      <json>
        <xsl:value-of select="xutil:xml-to-json($input)"/>
      </json>
      <full>
        <xsl:value-of select="xutil:xml-to-json($full)"/>
      </full>
    </top>
  </xsl:template>
</xsl:stylesheet>

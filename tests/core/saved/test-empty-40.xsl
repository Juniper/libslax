<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax-ext slax">
  <xsl:variable name="mine-temp-1">
    <a>alpha</a>
    <b>bravo</b>
    <c>charlie</c>
    <d>delta</d>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="mine" select="slax-ext:node-set($mine-temp-1)"/>
  <xsl:template match="/">
    <top>
      <xsl:variable name="two" select="&quot;two&quot;"/>
      <xsl:variable name="slax-ternary-1">
        <xsl:variable name="slax-ternary-1-cond" select="$two"/>
        <xsl:choose>
          <xsl:when test="$slax-ternary-1-cond">
            <xsl:copy-of select="$slax-ternary-1-cond"/>
          </xsl:when>
          <xsl:otherwise>
            <xsl:copy-of select="10"/>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="one" select="slax:value($slax-ternary-1)"/>
      <xsl:variable name="slax-ternary-2">
        <xsl:variable name="slax-ternary-2-cond" select="$one"/>
        <xsl:choose>
          <xsl:when test="$slax-ternary-2-cond">
            <xsl:copy-of select="$slax-ternary-2-cond"/>
          </xsl:when>
          <xsl:otherwise>
            <xsl:copy-of select="10"/>
          </xsl:otherwise>
        </xsl:choose>
      </xsl:variable>
      <three four="{slax:value($slax-ternary-2)}"/>
      <one>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:join(&quot;:&quot;, $mine)"/>
      </one>
      <two>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:join(&quot;%20&quot;, &quot;this&quot;, &quot;is&quot;, &quot;the&quot;, &quot;end&quot;, $mine/d)"/>
      </two>
      <three>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:join(/nothing, $mine)"/>
      </three>
      <four>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="concat(&quot;[&quot;, slax:join(&quot;] [&quot;, $mine), &quot;]&quot;)"/>
      </four>
      <xsl:variable name="f">
        <xsl:call-template name="answer"/>
      </xsl:variable>
      <five>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="concat(&quot;fish: &quot;, slax:join(&quot;, &quot;, $f))"/>
      </five>
    </top>
  </xsl:template>
  <xsl:template name="answer">
    <fish>one</fish>
    <fish>two</fish>
    <fish>red</fish>
    <fish>blue</fish>
  </xsl:template>
</xsl:stylesheet>

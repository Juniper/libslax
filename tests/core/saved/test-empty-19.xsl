<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" version="1.0" extension-element-prefixes="slax-ext">
  <xsl:template match="/">
    <top>
      <xsl:variable name="a">
        <xsl:call-template name="test">
          <xsl:with-param name="a" select="1"/>
        </xsl:call-template>
      </xsl:variable>
      <xsl:variable name="b-temp-1">
        <xsl:call-template name="test">
          <xsl:with-param name="a" select="2"/>
        </xsl:call-template>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="b" select="slax-ext:node-set($b-temp-1)"/>
      <xsl:variable name="c" select="call/call/call"/>
      <xsl:variable name="d">
        <xsl:call-template name="test">
          <xsl:with-param name="a" select="3"/>
        </xsl:call-template>
      </xsl:variable>
      <xsl:variable name="e" select="concat(call, call, call)"/>
      <xsl:variable name="f">
        <xsl:call-template name="_"/>
      </xsl:variable>
      <xsl:variable name="g" select="concat(call, /this/or/that)"/>
      <xsl:variable name="h" select="call"/>
      <xsl:variable name="i">
        <xsl:call-template name="call"/>
      </xsl:variable>
      <a>
        <xsl:value-of select="$a"/>
      </a>
      <b>
        <xsl:value-of select="$b"/>
      </b>
      <c>
        <xsl:value-of select="$c"/>
      </c>
      <d>
        <xsl:value-of select="$d"/>
      </d>
      <e>
        <xsl:value-of select="$e"/>
      </e>
      <f>
        <xsl:value-of select="$f"/>
      </f>
      <g>
        <xsl:value-of select="$g"/>
      </g>
      <h>
        <xsl:value-of select="$h"/>
      </h>
      <i>
        <xsl:value-of select="$i"/>
      </i>
      <xsl:call-template name="test">
        <xsl:with-param name="a" select="&quot;this and that&quot;"/>
      </xsl:call-template>
      <output>this and that</output>
      <output>
        <xsl:value-of select="string-length(&quot;this and that&quot;)"/>
      </output>
    </top>
  </xsl:template>
  <xsl:template name="test">
    <xsl:param name="a" select="&quot;A&quot;"/>
    <output>
      <xsl:value-of select="$a"/>
    </output>
  </xsl:template>
  <xsl:template name="_">
    <xsl:param name="a" select="&quot;A&quot;"/>
    <output>
      <xsl:value-of select="$a"/>
    </output>
  </xsl:template>
  <xsl:template name="call">
    <xsl:param name="a" select="&quot;call&quot;"/>
    <output>
      <xsl:value-of select="$a"/>
    </output>
  </xsl:template>
</xsl:stylesheet>

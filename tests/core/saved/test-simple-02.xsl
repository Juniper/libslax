<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:output indent="yes"/>
  <xsl:variable name="one" select="1"/>
  <xsl:variable name="two" select="2"/>
  <xsl:variable name="global">
    <xsl:choose>
      <xsl:when test="$one = $two">
        <answer>one</answer>
      </xsl:when>
      <xsl:otherwise>
        <answer>two</answer>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:template match="doc">
    <top>
      <xsl:call-template name="grand"/>
      <xsl:variable name="a">
        <xsl:call-template name="foo">
          <xsl:with-param name="min" select="10"/>
          <xsl:with-param name="max" select="15"/>
        </xsl:call-template>
      </xsl:variable>
      <xsl:variable name="b" select="&quot;10&quot;"/>
      <fish>
        <xsl:value-of select="fish"/>
      </fish>
      <trout>
        <xsl:value-of select="trout"/>
      </trout>
      <a>
        <xsl:copy-of select="$a"/>
      </a>
      <b>
        <xsl:value-of select="$b + 10"/>
      </b>
      <xsl:call-template name="foo">
        <xsl:with-param name="min" select="1"/>
        <xsl:with-param name="max" select="5"/>
      </xsl:call-template>
      <finale>ta da</finale>
    </top>
  </xsl:template>
  <xsl:template name="foo">
    <xsl:param name="min"/>
    <xsl:param name="max"/>
    <xsl:call-template name="goo">
      <xsl:with-param name="name" select="$min"/>
    </xsl:call-template>
    <xsl:if test="$min &lt; $max">
      <xsl:call-template name="foo">
        <xsl:with-param name="min" select="$min + 1"/>
        <xsl:with-param name="max" select="$max"/>
      </xsl:call-template>
    </xsl:if>
  </xsl:template>
  <xsl:template name="goo">
    <xsl:param name="name"/>
    <number>
      <xsl:value-of select="$name"/>
    </number>
  </xsl:template>
  <xsl:template name="grand">
    <just>grand</just>
  </xsl:template>
</xsl:stylesheet>

<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <xsl:param name="z" select="&quot;param to /&quot;"/>
    <xsl:variable name="a" select="1"/>
    <top>
      <xsl:variable name="b" select="&quot;local to match(/)&quot;"/>
      <xsl:call-template name="one">
        <xsl:with-param name="a" select="$a"/>
      </xsl:call-template>
    </top>
  </xsl:template>
  <xsl:template name="one">
    <xsl:param name="a"/>
    <xsl:param name="b"/>
    <xsl:variable name="c" select="&quot;local to one&quot;"/>
    <one>
      <xsl:value-of select="$a"/>
    </one>
    <xsl:call-template name="two">
      <xsl:with-param name="a" select="$a"/>
      <xsl:with-param name="c" select="&quot;arg to two&quot;"/>
      <xsl:with-param name="d" select="&quot;arg to two&quot;"/>
    </xsl:call-template>
  </xsl:template>
  <xsl:template name="two">
    <xsl:param name="a"/>
    <xsl:param name="c"/>
    <xsl:param name="d"/>
    <xsl:if test="$a &lt; 10">
      <xsl:call-template name="three">
        <xsl:with-param name="a" select="$a + 1"/>
      </xsl:call-template>
    </xsl:if>
  </xsl:template>
  <xsl:template name="three">
    <xsl:param name="a"/>
    <xsl:call-template name="one">
      <xsl:with-param name="a" select="$a"/>
      <xsl:with-param name="b" select="&quot;bee&quot;"/>
    </xsl:call-template>
  </xsl:template>
</xsl:stylesheet>

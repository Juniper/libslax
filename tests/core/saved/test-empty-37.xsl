<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <top>
      <xsl:variable name="tag" select="&quot;tag&quot;"/>
      <xsl:variable name="indent" select="&quot;indent&quot;"/>
      <xsl:apply-templates mode="line">
        <xsl:with-param name="tag" select="$tag"/>
        <xsl:with-param name="x" select="@x"/>
        <xsl:sort select="@order"/>
        <xsl:with-param name="y" select="@y"/>
        <xsl:with-param name="indent" select="$indent"/>
      </xsl:apply-templates>
    </top>
  </xsl:template>
  <xsl:template match="something">
    <xsl:for-each select="something/else">
      <xsl:sort select="@name/last"/>
      <xsl:sort select="@name/first"/>
      <xsl:sort select="@iq" order="descending" data-type="number"/>
      <xsl:sort select="@age" order="descending" data-type="number"/>
      <xsl:copy-of select="."/>
    </xsl:for-each>
  </xsl:template>
</xsl:stylesheet>

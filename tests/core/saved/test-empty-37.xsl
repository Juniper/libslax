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
</xsl:stylesheet>

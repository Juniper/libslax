<?xml version="1.0"?>
<xsl:stylesheet version="1.0"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform">

  <xsl:output indent="yes" method="text"/>
  <xsl:key name="item-by-value" match="item" use="."/>

  <xsl:template match="/">
    <xsl:apply-templates select="/root/item"/>
  </xsl:template>

  <xsl:template match="item">
    <xsl:if test="generate-id() = generate-id(key('item-by-value', normalize-space(.)))">
      <xsl:value-of select="."/>
      <xsl:text>
</xsl:text>
    </xsl:if>
  </xsl:template>

  <xsl:template match="text()">
    <xsl:apply-templates/>
  </xsl:template>
</xsl:stylesheet>


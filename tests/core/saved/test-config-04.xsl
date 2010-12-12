<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:temp="temp.example.com" version="1.0">
  <xsl:template match="configuration">
    <out>
      <xsl:variable name="name-servers" select="system/name-server/name"/>
      <xsl:call-template name="temp:ting"/>
      <xsl:call-template name="temp:ting">
        <xsl:with-param name="name-servers" select="$name-servers"/>
        <xsl:with-param name="size" select="count($name-servers)"/>
      </xsl:call-template>
      <xsl:call-template name="temp:ting">
        <xsl:with-param name="name-servers" select="$name-servers"/>
        <xsl:with-param name="size" select="count($name-servers)"/>
      </xsl:call-template>
    </out>
  </xsl:template>
  <xsl:template name="temp:ting">
    <xsl:param name="name-servers"/>
    <xsl:param name="size" select="0"/>
    <output>
      <xsl:value-of select="concat(&quot;template called with size &quot;, $size)"/>
    </output>
  </xsl:template>
</xsl:stylesheet>

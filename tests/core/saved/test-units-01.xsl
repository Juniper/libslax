<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/top">
    <top>
      <xsl:apply-templates select="*"/>
    </top>
  </xsl:template>
  <xsl:template match="measurement">
    <xsl:variable name="m">
      <xsl:choose>
        <xsl:when test="@fromunit = &quot;km&quot;">
          <xsl:value-of select=". * 1000"/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;m&quot;">
          <xsl:value-of select="."/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;cm&quot;">
          <xsl:value-of select=". * 0.01"/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;mm&quot;">
          <xsl:value-of select=". * 0.001"/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;mi&quot;">
          <xsl:value-of select=". div 0.00062137"/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;yd&quot;">
          <xsl:value-of select=". div 1.09361"/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;ft&quot;">
          <xsl:value-of select=". div 3.2808"/>
        </xsl:when>
        <xsl:when test="@fromunit = &quot;in&quot;">
          <xsl:value-of select=". div 39.37"/>
        </xsl:when>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="v">
      <xsl:choose>
        <xsl:when test="@tounit = &quot;km&quot;">
          <xsl:value-of select="$m div 1000"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;m&quot;">
          <xsl:value-of select="$m"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;cm&quot;">
          <xsl:value-of select="$m div 0.01"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;mm&quot;">
          <xsl:value-of select="$m div 0.001"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;mi&quot;">
          <xsl:value-of select="$m * 0.00062137"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;yd&quot;">
          <xsl:value-of select="$m * 1.09361"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;ft&quot;">
          <xsl:value-of select="$m * 3.2808"/>
        </xsl:when>
        <xsl:when test="@tounit = &quot;in&quot;">
          <xsl:value-of select="$m * 39.37"/>
        </xsl:when>
      </xsl:choose>
    </xsl:variable>
    <measurement unit="{@tounit}">
      <xsl:number value="$v" grouping-separator="," grouping-size="3" format="1"/>
    </measurement>
  </xsl:template>
</xsl:stylesheet>

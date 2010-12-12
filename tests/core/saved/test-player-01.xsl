<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:output indent="yes"/>
  <xsl:template match="/roster">
    <out>
      <xsl:variable name="x">
        <xsl:apply-templates/>
      </xsl:variable>
      <x>
        <xsl:copy-of select="$x"/>
      </x>
    </out>
  </xsl:template>
  <xsl:template match="player">
    <xsl:if test="salary">
      <player last="{last}" first="{first}">
        <salary>
          <xsl:value-of select="salary"/>
        </salary>
        <xsl:variable name="salary" select="salary"/>
        <count>
          <xsl:value-of select="count(../player[salary &gt; $salary])"/>
        </count>
      </player>
    </xsl:if>
  </xsl:template>
  <xsl:template match="text()"/>
</xsl:stylesheet>

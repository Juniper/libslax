<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <xsl:variable name="location" select="location"/>
    <top>
      <element attr1="one" attr2="two"/>
      <location state="{$location/state}" zip="{$location/zip5}-{$location/zip4}"/>
      <avt sign="{{here}}"/>
    </top>
  </xsl:template>
</xsl:stylesheet>

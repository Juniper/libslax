<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:output method="xml" indent="yes" media-type="image/svg"/>
  <xsl:template match="configuration">
    <top>
      <xsl:for-each select="interfaces/interface">
        <xsl:sort select="name"/>
        <xsl:copy-of select="."/>
      </xsl:for-each>
    </top>
  </xsl:template>
</xsl:stylesheet>

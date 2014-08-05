<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:import href="saved/test-empty-01.xsl"/>
  <xsl:param name="x" select="1"/>
  <xsl:include href="saved/test-empty-02.xsl"/>
  <xsl:template match="/">
    <xsl:apply-imports/>
  </xsl:template>
</xsl:stylesheet>

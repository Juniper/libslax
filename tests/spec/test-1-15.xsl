<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:include href="foo.slax"/>
  <xsl:import href="goo.xsl"/>
  <xsl:template match="/">
    <xsl:apply-imports/>
  </xsl:template>
</xsl:stylesheet>
